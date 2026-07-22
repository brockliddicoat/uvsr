#include "imgui.h"
#include "imgui_internal.h"

#include <cfloat>
#include <cmath>
#include <iostream>

namespace
{
    constexpr float FixedDeltaSeconds = 1.0f / 60.0f;
    constexpr float ComboWidth = 180.0f;
    constexpr float PopupHeight = 100.0f;
    constexpr int MaximumTransitionFrames = 64;

    struct ComboHarness
    {
        ImGuiID comboId = 0;
        ImGuiID otherComboId = 0;
        ImGuiID popupId = 0;
        int selectedItem = -1;
        int optionPressCount = 0;
    };

    struct FrameObservation
    {
        bool popupBegan = false;
        bool optionPressed = false;
        bool transitionActive = false;
        bool popupOpen = false;
        ImGuiID optionId = 0;
        ImGuiID activeId = 0;
        ImRect comboRect;
        ImRect optionRect;
        ImVec2 popupPosition;
        ImVec2 popupSize;
        float rollAmount = -1.0f;
        int interactionReady = -1;
        int closing = -1;
        int rollFromBottom = -1;
    };

    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= 1e-5f;
    }

    void QueueMouse(const ImVec2& position, bool down)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    }

    ImVec2 Center(const ImRect& rect)
    {
        return ImVec2(
            (rect.Min.x + rect.Max.x) * 0.5f,
            (rect.Min.y + rect.Max.y) * 0.5f);
    }

    FrameObservation SubmitComboFrame(
        ComboHarness& harness,
        float ownerY,
        bool forceOpen,
        bool clipCombo = false)
    {
        FrameObservation observation;

        ImGui::NewFrame();
        ImGui::SetNextWindowPos(
            ImVec2(30.0f, ownerY),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(240.0f, 60.0f),
            ImGuiCond_Always);
        const ImGuiWindowFlags ownerFlags =
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        const bool ownerVisible = ImGui::Begin(
            "Dropdown Roll Test Owner",
            nullptr,
            ownerFlags);
        if (ownerVisible)
        {
            harness.comboId = ImGui::GetID("##Mode");
            harness.otherComboId = ImGui::GetID("##OtherMode");
            harness.popupId = ImHashStr(
                "##ComboPopup",
                0,
                harness.comboId);
            if (forceOpen)
                ImGui::OpenPopupEx(
                    harness.popupId,
                    ImGuiPopupFlags_None);
            if (clipCombo)
                ImGui::SetCursorScreenPos(ImVec2(38.0f, 1000.0f));

            ImGui::SetNextItemWidth(ComboWidth);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(ComboWidth, PopupHeight),
                ImVec2(ComboWidth, PopupHeight));
            const ImVec2 comboMin = ImGui::GetCursorScreenPos();
            observation.comboRect = ImRect(
                comboMin,
                ImVec2(
                    comboMin.x + ComboWidth,
                    comboMin.y + ImGui::GetFrameHeight()));

            const char* preview =
                harness.selectedItem == 0 ? "Option A" : "None";
            observation.popupBegan = ImGui::BeginCombo(
                "##Mode",
                preview);
            if (observation.popupBegan)
            {
                ImGuiWindow* popupWindow = ImGui::GetCurrentWindow();
                observation.popupPosition = popupWindow->Pos;
                observation.popupSize = popupWindow->Size;

                observation.optionPressed = ImGui::Selectable(
                    "Option A",
                    harness.selectedItem == 0);
                observation.optionId = ImGui::GetItemID();
                observation.optionRect = ImRect(
                    ImGui::GetItemRectMin(),
                    ImGui::GetItemRectMax());
                if (observation.optionPressed)
                {
                    harness.selectedItem = 0;
                    ++harness.optionPressCount;
                }
                ImGui::Selectable(
                    "Option B",
                    harness.selectedItem == 1);
                ImGui::Selectable(
                    "Option C",
                    harness.selectedItem == 2);

                // Read the state owned by the production popup after item
                // behavior has had an opportunity to start roll-up.
                observation.rollAmount =
                    popupWindow->StateStorage.GetFloat(
                        ImHashStr("##ComboPopupRollAmount"),
                        -1.0f);
                observation.interactionReady =
                    popupWindow->StateStorage.GetInt(
                        ImHashStr("##ComboPopupInteractionReady"),
                        -1);
                observation.closing =
                    popupWindow->StateStorage.GetInt(
                        ImHashStr("##ComboPopupClosing"),
                        -1);
                observation.rollFromBottom =
                    popupWindow->StateStorage.GetInt(
                        ImHashStr("##ComboPopupRollFromBottom"),
                        -1);
                observation.activeId = GImGui->ActiveId;
                ImGui::EndCombo();
            }

            observation.transitionActive =
                ImGui::IsComboPopupTransitionActive(
                    harness.comboId);
            observation.popupOpen = ImGui::IsPopupOpen(
                harness.popupId,
                ImGuiPopupFlags_None);
        }
        ImGui::End();
        ImGui::Render();
        return observation;
    }
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(480.0f, 360.0f);
    io.DeltaTime = FixedDeltaSeconds;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(
        &fontPixels,
        &fontWidth,
        &fontHeight);

    bool passed = true;
    ComboHarness harness;
    const ImVec2 outside(-FLT_MAX, -FLT_MAX);

    QueueMouse(outside, false);
    FrameObservation opening = SubmitComboFrame(
        harness,
        30.0f,
        true);
    passed &= Check(
        opening.popupBegan && opening.popupOpen,
        "the real combo popup opens in the headless frame harness");
    passed &= Check(
        opening.transitionActive &&
            opening.interactionReady == 0 &&
            Near(opening.rollAmount, 0.0f),
        "roll-down begins at its blocked hidden endpoint");

    // Give the newly created popup one frame to settle at its constrained
    // position before targeting the actual selectable rectangle.
    QueueMouse(outside, false);
    FrameObservation settledOpening = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        settledOpening.popupBegan &&
            settledOpening.transitionActive &&
            settledOpening.interactionReady == 0,
        "roll-down remains interaction-blocked before completion");

    const ImVec2 optionCenter = Center(settledOpening.optionRect);
    QueueMouse(optionCenter, true);
    FrameObservation blockedDown = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        blockedDown.transitionActive &&
            blockedDown.interactionReady == 0,
        "the test mouse-down lands during the production roll-down block");
    passed &= Check(
        blockedDown.activeId != blockedDown.optionId,
        "a blocked option mouse-down cannot acquire active ownership");
    passed &= Check(
        !blockedDown.optionPressed &&
            harness.optionPressCount == 0 &&
            harness.selectedItem == -1,
        "a blocked option mouse-down cannot select immediately");

    FrameObservation ready = blockedDown;
    bool pressedWhileHeld = false;
    int rollDownFrames = 0;
    while (ready.transitionActive &&
        rollDownFrames < MaximumTransitionFrames)
    {
        QueueMouse(optionCenter, true);
        ready = SubmitComboFrame(harness, 30.0f, false);
        pressedWhileHeld |= ready.optionPressed;
        ++rollDownFrames;
    }
    passed &= Check(
        rollDownFrames < MaximumTransitionFrames,
        "roll-down reaches an endpoint without a duplicated test clock");
    passed &= Check(
        ready.popupBegan &&
            ready.popupOpen &&
            !ready.transitionActive &&
            ready.interactionReady == 1 &&
            Near(ready.rollAmount, 1.0f),
        "the transition query clears only at the interactive roll-down endpoint");
    passed &= Check(
        !pressedWhileHeld && harness.optionPressCount == 0,
        "holding blocked input through readiness cannot replay a selection");

    QueueMouse(optionCenter, false);
    FrameObservation staleRelease = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        !staleRelease.optionPressed &&
            harness.optionPressCount == 0 &&
            harness.selectedItem == -1,
        "releasing after readiness cannot replay the blocked mouse-down");

    // Prove that the harness can still perform a normal click after the stale
    // input was discarded, and that the click enters the real roll-up path.
    QueueMouse(optionCenter, true);
    FrameObservation validDown = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        validDown.activeId == validDown.optionId &&
            !validDown.optionPressed,
        "an ordinary ready-state mouse-down acquires the selectable");

    QueueMouse(optionCenter, false);
    FrameObservation selection = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        selection.optionPressed &&
            harness.optionPressCount == 1 &&
            harness.selectedItem == 0,
        "an ordinary click selects exactly once");
    passed &= Check(
        selection.popupOpen &&
            selection.transitionActive &&
            selection.closing == 1 &&
            selection.interactionReady == 0 &&
            Near(selection.rollAmount, 1.0f),
        "selection starts roll-up at its visible blocked endpoint");

    FrameObservation closing = selection;
    bool sawHiddenRollUpEndpoint = false;
    int rollUpFrames = 0;
    while (closing.transitionActive &&
        rollUpFrames < MaximumTransitionFrames)
    {
        QueueMouse(outside, false);
        closing = SubmitComboFrame(harness, 30.0f, false);
        if (closing.popupBegan &&
            closing.closing == 1 &&
            Near(closing.rollAmount, 0.0f))
        {
            sawHiddenRollUpEndpoint = true;
        }
        ++rollUpFrames;
    }
    passed &= Check(
        sawHiddenRollUpEndpoint,
        "roll-up presents its hidden endpoint before popup removal");
    passed &= Check(
        rollUpFrames < MaximumTransitionFrames &&
            !closing.transitionActive &&
            !closing.popupOpen &&
            !closing.popupBegan,
        "the transition query clears after roll-up removes the popup");

    // Open a second lifecycle through the combo's real button behavior, then
    // move the owner far enough that ImGui relocates the popup above it. The
    // reveal edge must remain the direction captured when this lifecycle
    // opened.
    QueueMouse(outside, false);
    FrameObservation closedOwner = SubmitComboFrame(
        harness,
        30.0f,
        false);
    const ImVec2 comboCenter = Center(closedOwner.comboRect);
    QueueMouse(comboCenter, true);
    FrameObservation openerDown = SubmitComboFrame(
        harness,
        30.0f,
        false);
    passed &= Check(
        !openerDown.popupOpen,
        "the combo opener uses click-release behavior");
    QueueMouse(comboCenter, false);
    FrameObservation directionStart = SubmitComboFrame(
        harness,
        30.0f,
        false);
    const bool startedAboveOwner =
        directionStart.popupPosition.y +
            directionStart.popupSize.y <=
        directionStart.comboRect.Min.y + 0.5f;
    if (!(directionStart.popupBegan &&
        directionStart.rollFromBottom == 0 &&
        !startedAboveOwner))
    {
        std::cerr
            << "direction start: began="
            << directionStart.popupBegan
            << " open="
            << directionStart.popupOpen
            << " active="
            << directionStart.transitionActive
            << " rollFromBottom="
            << directionStart.rollFromBottom
            << " popupY="
            << directionStart.popupPosition.y
            << " popupHeight="
            << directionStart.popupSize.y
            << " comboMinY="
            << directionStart.comboRect.Min.y
            << " comboMaxY="
            << directionStart.comboRect.Max.y
            << '\n';
    }
    passed &= Check(
        directionStart.popupBegan &&
            directionStart.rollFromBottom == 0 &&
            !startedAboveOwner,
        "a popup not placed above its owner captures a top-edge roll direction");

    QueueMouse(outside, false);
    FrameObservation directionMoved = SubmitComboFrame(
        harness,
        295.0f,
        false);
    const bool movedAboveOwner =
        directionMoved.popupPosition.y +
            directionMoved.popupSize.y <=
        directionMoved.comboRect.Min.y + 0.5f;
    passed &= Check(
        directionMoved.popupBegan && movedAboveOwner,
        "the live popup relocates above a moved owner");
    passed &= Check(
        directionMoved.rollFromBottom ==
            directionStart.rollFromBottom,
        "RollFromBottom remains latched for the popup lifecycle");

    // A different combo ID must be a no-op even if the active owner is omitted
    // for a frame. Keep a regular host window submitted while exercising the
    // public finish API directly.
    QueueMouse(outside, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(30.0f, 295.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 60.0f), ImGuiCond_Always);
    ImGui::Begin(
        "Dropdown Roll Test Owner",
        nullptr,
        ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar);
    const int popupCountBeforeFinish = GImGui->OpenPopupStack.Size;
    ImGui::FinishComboPopupTransition(harness.otherComboId);
    const int popupCountAfterWrongFinish = GImGui->OpenPopupStack.Size;
    const bool activeAfterWrongFinish =
        ImGui::IsComboPopupTransitionActive(harness.comboId);
    if (!(popupCountAfterWrongFinish == popupCountBeforeFinish &&
        activeAfterWrongFinish))
    {
        std::cerr
            << "wrong finish: before="
            << popupCountBeforeFinish
            << " after="
            << popupCountAfterWrongFinish
            << " active="
            << activeAfterWrongFinish
            << '\n';
    }
    passed &= Check(
        popupCountAfterWrongFinish == popupCountBeforeFinish &&
            activeAfterWrongFinish,
        "finishing a different combo cannot disturb the active owner");
    ImGui::End();
    ImGui::Render();

    // The production BeginCombo path must finish the exact popup when ItemAdd
    // reports that its owner is clipped. This is the normal cleanup path for a
    // scrolling owner that disappears from the submitted viewport.
    QueueMouse(outside, false);
    FrameObservation clippedOwner = SubmitComboFrame(
        harness,
        295.0f,
        false,
        true);
    if (!(popupCountBeforeFinish == 1 &&
        GImGui->OpenPopupStack.Size == 0 &&
        !clippedOwner.popupBegan &&
        !clippedOwner.popupOpen &&
        !clippedOwner.transitionActive))
    {
        std::cerr
            << "clipped finish: before="
            << popupCountBeforeFinish
            << " stack="
            << GImGui->OpenPopupStack.Size
            << " began="
            << clippedOwner.popupBegan
            << " open="
            << clippedOwner.popupOpen
            << " active="
            << clippedOwner.transitionActive
            << '\n';
    }
    passed &= Check(
        popupCountBeforeFinish == 1 &&
            GImGui->OpenPopupStack.Size == 0 &&
            !clippedOwner.popupBegan &&
            !clippedOwner.popupOpen &&
            !clippedOwner.transitionActive &&
            !ImGui::IsComboPopupTransitionActive(harness.comboId) &&
            !ImGui::IsPopupOpen(
                harness.popupId,
                ImGuiPopupFlags_None),
        "a clipped exact owner finishes and removes its popup state");

    // The sibling SkipItems branch has the same scoped cleanup obligation.
    // Reopen once, collapse the owner, and still call BeginCombo so the patched
    // early-out itself is what closes the popup.
    QueueMouse(outside, false);
    FrameObservation reopenedForCollapse = SubmitComboFrame(
        harness,
        30.0f,
        true);
    passed &= Check(
        reopenedForCollapse.popupOpen &&
            reopenedForCollapse.transitionActive,
        "the popup reopens before the collapsed-owner cleanup check");

    QueueMouse(outside, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(30.0f, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 60.0f), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
    const bool collapsedOwnerVisible = ImGui::Begin(
        "Dropdown Roll Test Owner",
        nullptr,
        ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar);
    const bool collapsedComboBegan = ImGui::BeginCombo(
        "##Mode",
        "Option A");
    passed &= Check(
        !collapsedOwnerVisible &&
            !collapsedComboBegan &&
            GImGui->OpenPopupStack.Size == 0 &&
            !ImGui::IsComboPopupTransitionActive(harness.comboId),
        "a SkipItems exact owner finishes and removes its popup state");
    ImGui::End();
    ImGui::Render();

    ImGui::DestroyContext();
    if (!passed)
        return 1;

    std::cout << "ImGui dropdown roll lifecycle tests passed\n";
    return 0;
}
