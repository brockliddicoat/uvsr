#include "ui_animation.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

namespace
{
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
}

int main()
{
    using namespace uvsr;

    bool passed = true;

    struct ResetPlacementCase
    {
        bool isDropdown;
        std::size_t nestedDepth;
        bool expectedInGutter;
    };
    constexpr ResetPlacementCase resetPlacementCases[] = {
        { false, 0u, false },
        { false, 1u, false },
        { false, 2u, false },
        { true, 0u, false },
        { true, 1u, true },
        { true, 2u, true }
    };
    bool resetPlacementMatrixMatches = true;
    for (const ResetPlacementCase& placementCase : resetPlacementCases)
    {
        resetPlacementMatrixMatches &=
            ShouldPlaceUiResetInNestedDropdownGutter(
                placementCase.isDropdown,
                placementCase.nestedDepth) ==
            placementCase.expectedInGutter;
    }
    passed &= Check(
        resetPlacementMatrixMatches,
        "only dropdowns at a nonzero nested depth use the leading gutter");

    constexpr float nestedComboLeft = 62.f;
    constexpr float nestedIndentSpacing = 25.f;
    constexpr float resetButtonSize = 20.f;
    const float nestedResetOffset = ResolveNestedDropdownResetOffset(
        nestedIndentSpacing,
        resetButtonSize);
    const float nestedResetLeft = nestedComboLeft + nestedResetOffset;
    const float nestedResetRight = nestedResetLeft + resetButtonSize;
    const float nestedGutterLeft = nestedComboLeft - nestedIndentSpacing;
    passed &= Check(
        Near(nestedResetOffset, -22.5f) &&
            Near(nestedResetLeft - nestedGutterLeft, 2.5f) &&
            Near(nestedComboLeft - nestedResetRight, 2.5f),
        "the nested reset button is centered wholly inside the preceding "
        "indent gutter");
    constexpr float contentRight = 516.f;
    constexpr float comboWidth = 267.f;
    constexpr float itemInnerSpacing = 5.f;
    const float topLevelComboLeft =
        nestedComboLeft - nestedIndentSpacing;
    const float topLevelLabelWidth =
        contentRight - nestedIndentSpacing -
        (topLevelComboLeft + comboWidth + itemInnerSpacing);
    const float nestedLabelWidth =
        contentRight -
        (nestedComboLeft + comboWidth + itemInnerSpacing);
    passed &= Check(
        Near(topLevelLabelWidth, nestedLabelWidth),
        "moving the nested reset into one indent lane restores the same "
        "label width as an un-nested dropdown");

    UiDrawerHeightDeltas expansion;
    expansion = AccumulateUiDrawerHeightDelta(
        expansion,
        100.f,
        20.f,
        80.f,
        179.5f);
    passed &= Check(
        Near(expansion.total, 60.f) &&
            Near(expansion.aboveViewport, 60.f),
        "a wholly-above expansion contributes to both height deltas");

    UiDrawerHeightDeltas collapse;
    collapse = AccumulateUiDrawerHeightDelta(
        collapse,
        100.f,
        80.f,
        0.f,
        179.5f);
    passed &= Check(
        Near(collapse.total, -80.f) &&
            Near(collapse.aboveViewport, -80.f),
        "a wholly-above collapse to zero preserves its negative delta");

    UiDrawerHeightDeltas viewportRelationship;
    viewportRelationship = AccumulateUiDrawerHeightDelta(
        viewportRelationship,
        100.f,
        20.f,
        80.f,
        150.f);
    viewportRelationship = AccumulateUiDrawerHeightDelta(
        viewportRelationship,
        220.f,
        50.f,
        10.f,
        150.f);
    passed &= Check(
        Near(viewportRelationship.total, 20.f) &&
            Near(viewportRelationship.aboveViewport, 0.f),
        "intersecting and below-viewport bodies never move the viewport anchor");

    const UiDrawerHeightDeltas anchoring{ 48.f, 13.f };
    passed &= Check(
        Near(ResolveUiScrollAnchorDelta(anchoring, false), 13.f),
        "ordinary scrolling follows only wholly-above drawer changes");
    passed &= Check(
        Near(ResolveUiScrollAnchorDelta(anchoring, true), 48.f),
        "bottom preservation follows the complete displayed-height change");

    passed &= Check(
        !ShouldRetainUiViewportHeight(false, false, false),
        "layout animation alone does not pin the Settings viewport");
    passed &= Check(
        ShouldRetainUiViewportHeight(true, false, false),
        "a nonzero scroll offset retains the Settings viewport");
    passed &= Check(
        ShouldRetainUiViewportHeight(false, true, false),
        "wheel input retains the Settings viewport");
    passed &= Check(
        ShouldRetainUiViewportHeight(false, false, true),
        "scrollbar dragging retains the Settings viewport");

    UiComboPopupRollState popupRoll =
        RequestUiComboPopupRollDown();
    passed &= Check(
        IsUiComboPopupRollActive(popupRoll) &&
            !IsUiComboPopupInteractionReady(popupRoll) &&
            Near(GetUiComboPopupVisibleAmount(popupRoll), 0.f),
        "a dropdown begins closed, rolling down, and non-interactive");
    popupRoll = AdvanceUiComboPopupRoll(popupRoll, 1.f);
    passed &= Check(
        popupRoll.phase == UiComboPopupRollPhase::RollingDown &&
            popupRoll.elapsedSeconds <=
                UiComboPopupRollMaximumDeltaSeconds &&
            GetUiComboPopupVisibleAmount(popupRoll) > 0.f &&
            GetUiComboPopupVisibleAmount(popupRoll) < 1.f,
        "a slow frame cannot skip the geometric dropdown roll");
    for (int step = 0; step < 5; ++step)
    {
        popupRoll = AdvanceUiComboPopupRoll(
            popupRoll,
            UiComboPopupRollMaximumDeltaSeconds);
    }
    passed &= Check(
        popupRoll.phase == UiComboPopupRollPhase::Open &&
            IsUiComboPopupInteractionReady(popupRoll) &&
            Near(GetUiComboPopupVisibleAmount(popupRoll), 1.f),
        "the completed roll-down exposes a fully opaque interactive popup");
    popupRoll = RequestUiComboPopupRollUp();
    passed &= Check(
        IsUiComboPopupRollActive(popupRoll) &&
            !IsUiComboPopupInteractionReady(popupRoll) &&
            Near(GetUiComboPopupVisibleAmount(popupRoll), 1.f),
        "selection begins a fully visible but non-interactive roll-up");
    for (int step = 0; step < 6; ++step)
    {
        popupRoll = AdvanceUiComboPopupRoll(
            popupRoll,
            UiComboPopupRollMaximumDeltaSeconds);
    }
    passed &= Check(
        popupRoll.phase == UiComboPopupRollPhase::Closed &&
            !IsUiComboPopupRollActive(popupRoll) &&
            Near(GetUiComboPopupVisibleAmount(popupRoll), 0.f),
        "the roll-up reaches a true hidden endpoint before popup closure");

    int dropdownIdleStart = -1;
    dropdownIdleStart = UpdateUiDropdownIdleStartFrame(
        dropdownIdleStart,
        40,
        false);
    passed &= Check(
        dropdownIdleStart == -1,
        "an active UI composition cannot arm a dropdown commit");
    dropdownIdleStart = UpdateUiDropdownIdleStartFrame(
        dropdownIdleStart,
        41,
        true);
    passed &= Check(
        dropdownIdleStart == 41,
        "the first idle dropdown frame only arms the commit");
    dropdownIdleStart = UpdateUiDropdownIdleStartFrame(
        dropdownIdleStart,
        41,
        true);
    passed &= Check(
        dropdownIdleStart == 41,
        "re-evaluating one frame cannot advance the idle barrier");
    passed &= Check(
        !ShouldCommitDeferredDropdownActions(
            41,
            39,
            dropdownIdleStart,
            UiDropdownSelectionSettleSeconds),
        "the first idle frame is always presentation-only");
    passed &= Check(
        ShouldCommitDeferredDropdownActions(
            42,
            39,
            dropdownIdleStart,
            UiDropdownSelectionSettleSeconds),
        "the frame after a presented idle endpoint may commit");
    dropdownIdleStart = UpdateUiDropdownIdleStartFrame(
        dropdownIdleStart,
        42,
        false);
    passed &= Check(
        dropdownIdleStart == -1,
        "new animation or interaction disarms a pending commit");
    passed &= Check(
        !ShouldCommitDeferredDropdownActions(
            60,
            60,
            58,
            UiDropdownSelectionSettleSeconds),
        "a dropdown never commits on its request frame");
    passed &= Check(
        !ShouldCommitDeferredDropdownActions(
            60,
            50,
            58,
            UiDropdownSelectionSettleSeconds - 0.001),
        "the full dropdown selection-settle interval is required");

    using TestDeferredQueue =
        DeferredUiActionQueue<int, std::function<void()>>;
    const auto applyDeferredAction =
        [](int, std::function<void()> action)
        {
            action();
        };

    TestDeferredQueue replacementQueue;
    std::vector<int> replacementResults;
    replacementQueue.Upsert(
        17,
        [&replacementResults]()
        {
            replacementResults.push_back(1);
        });
    replacementQueue.Upsert(
        17,
        [&replacementResults]()
        {
            replacementResults.push_back(2);
        });
    passed &= Check(
        replacementQueue.Size() == 1,
        "a newer action replaces an older action with the same key");
    passed &= Check(
        replacementQueue.Drain(applyDeferredAction) &&
            replacementResults == std::vector<int>{ 2 },
        "only the newest same-key action is applied");
    passed &= Check(
        !replacementQueue.Drain(applyDeferredAction),
        "a second drain is a no-op after the queue is consumed");

    TestDeferredQueue orderedQueue;
    std::vector<int> orderedResults;
    orderedQueue.Upsert(
        7,
        [&orderedResults]()
        {
            orderedResults.push_back(7);
        });
    orderedQueue.Upsert(
        3,
        [&orderedResults]()
        {
            orderedResults.push_back(3);
        });
    orderedQueue.Drain(applyDeferredAction);
    passed &= Check(
        orderedResults == std::vector<int>({ 7, 3 }),
        "different keys retain their insertion order");

    TestDeferredQueue reentrantQueue;
    std::vector<int> reentrantResults;
    bool emptyDuringCallback = false;
    reentrantQueue.Upsert(
        1,
        [&reentrantQueue, &reentrantResults, &emptyDuringCallback]()
        {
            emptyDuringCallback = reentrantQueue.Empty();
            reentrantResults.push_back(1);
            reentrantQueue.Upsert(
                2,
                [&reentrantResults]()
                {
                    reentrantResults.push_back(2);
                });
        });
    reentrantQueue.Drain(applyDeferredAction);
    passed &= Check(
        emptyDuringCallback &&
            reentrantQueue.Size() == 1 &&
            reentrantResults == std::vector<int>{ 1 },
        "draining clears current work before a callback can enqueue more");
    reentrantQueue.Drain(applyDeferredAction);
    passed &= Check(
        reentrantQueue.Empty() &&
            reentrantResults == std::vector<int>({ 1, 2 }),
        "reentrant work remains queued for the next drain");

    struct TestAliasingSettings
    {
        int method = 0;
        int quality = 0;
        int retainedOverride = 0;
    };
    const auto aliasingLayoutSignature =
        [](const TestAliasingSettings& settings)
        {
            // Stand in for the method-gated body plus its quality-dependent
            // controls. Equal signatures mean the same layout predicates.
            return settings.method * 100 + settings.quality * 10;
        };

    TestAliasingSettings committedAliasing{ 0, 1, 23 };
    DeferredUiPresentation<TestAliasingSettings>
        aliasingPresentation;
    aliasingPresentation.Stage(
        committedAliasing,
        [](TestAliasingSettings& staged)
        {
            staged.method = 2;
            staged.quality = 3;
        });
    passed &= Check(
        aliasingPresentation.HasPending() &&
            committedAliasing.method == 0 &&
            committedAliasing.quality == 1 &&
            committedAliasing.retainedOverride == 23,
        "staging an Aliasing choice leaves renderer settings untouched");
    passed &= Check(
        aliasingLayoutSignature(
            aliasingPresentation.Present(committedAliasing)) == 230 &&
            aliasingLayoutSignature(committedAliasing) == 10,
        "pending Aliasing method and quality drive presentation before commit");

    aliasingPresentation.Stage(
        committedAliasing,
        [](TestAliasingSettings& staged)
        {
            staged.quality = 2;
        });
    passed &= Check(
        aliasingPresentation.Present(committedAliasing).method == 2 &&
            aliasingPresentation.Present(committedAliasing).quality == 2 &&
            aliasingPresentation.Present(committedAliasing).
                retainedOverride == 23,
        "later staged edits compose on the pending Aliasing snapshot");

    const int stagedAliasingLayout = aliasingLayoutSignature(
        aliasingPresentation.Present(committedAliasing));
    passed &= Check(
        aliasingPresentation.CommitTo(committedAliasing) &&
            !aliasingPresentation.HasPending() &&
            aliasingLayoutSignature(
                aliasingPresentation.Present(committedAliasing)) ==
                    stagedAliasingLayout,
        "committing the presented Aliasing snapshot cannot trigger a second reflow");
    passed &= Check(
        !aliasingPresentation.CommitTo(committedAliasing),
        "committing an Aliasing presentation twice is a no-op");

    aliasingPresentation.Stage(
        committedAliasing,
        [](TestAliasingSettings& staged)
        {
            staged.method = 1;
        });
    aliasingPresentation.Cancel();
    passed &= Check(
        !aliasingPresentation.HasPending() &&
            aliasingPresentation.Present(committedAliasing).method == 2,
        "canceling a staged Aliasing choice restores committed presentation");

    using StructuralAliasingPresentation =
        DeferredUiStructuralPresentation<TestAliasingSettings>;
    using StructuralAliasingPhase =
        DeferredUiStructuralPresentationPhase;

    TestAliasingSettings structuralCommitted{ 0, 1, 41 };
    StructuralAliasingPresentation structuralPresentation;
    structuralPresentation.Stage(
        structuralCommitted,
        true,
        100,
        [](TestAliasingSettings& staged)
        {
            staged.method = 2;
            staged.quality = 3;
        });
    passed &= Check(
        structuralPresentation.GetPhase() ==
                StructuralAliasingPhase::AwaitPopupRollUp &&
            structuralPresentation.ShowStructuralBody() &&
            !structuralPresentation.ReadyForCommit() &&
            structuralPresentation.
                PresentSelectors(structuralCommitted).method == 2 &&
            structuralPresentation.
                PresentStructuralBody(structuralCommitted).method == 0 &&
            structuralCommitted.method == 0,
        "a structural Aliasing choice presents its selector while retaining the committed body through popup roll-up");

    structuralPresentation.Advance(100, true, false);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::AwaitPopupRollUp,
        "the structural request frame cannot bypass popup roll-up");
    passed &= Check(
        !structuralPresentation.CommitTo(structuralCommitted) &&
            structuralCommitted.method == 0,
        "a structural choice cannot commit while its popup is rolling up");
    structuralPresentation.Advance(101, true, false);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::AwaitPopupRollUp,
        "an active popup transition holds the committed drawer body open");
    structuralPresentation.Advance(101, false, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::AwaitPopupRollUp,
        "a finished popup transition still waits for stable scrolling and layout before drawer collapse");
    structuralPresentation.Advance(101, true, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
                StructuralAliasingPhase::CollapseCommitted &&
            !structuralPresentation.ShowStructuralBody() &&
            !structuralPresentation.ReadyForCommit() &&
            structuralPresentation.
                PresentStructuralBody(structuralCommitted).method == 0,
        "popup closure starts a separate collapse of the committed drawer body");

    structuralPresentation.Advance(101, true, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::CollapseCommitted,
        "the collapse phase must be composed for at least one later frame");
    structuralPresentation.Advance(102, false, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::CollapseCommitted,
        "an unstable committed layout cannot begin staged expansion");
    structuralPresentation.Advance(102, true, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
                StructuralAliasingPhase::ExpandStaged &&
            structuralPresentation.ShowStructuralBody() &&
            !structuralPresentation.ReadyForCommit() &&
            structuralPresentation.
                PresentStructuralBody(structuralCommitted).method == 2,
        "a stable collapsed layout advances to the staged structural body");

    structuralPresentation.Advance(102, true, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
            StructuralAliasingPhase::ExpandStaged,
        "the expansion phase must be composed for at least one later frame");
    structuralPresentation.Advance(103, false, true);
    passed &= Check(
        !structuralPresentation.ReadyForCommit() &&
            !structuralPresentation.CommitTo(structuralCommitted) &&
            structuralCommitted.method == 0,
        "an unstable staged layout remains blocked from renderer commit");
    structuralPresentation.Advance(103, true, true);
    passed &= Check(
        structuralPresentation.GetPhase() ==
                StructuralAliasingPhase::ReadyToCommit &&
            structuralPresentation.ReadyForCommit(),
        "only a stable staged layout becomes ready for renderer commit");

    const int structuralLayoutBeforeCommit = aliasingLayoutSignature(
        structuralPresentation.
            PresentStructuralBody(structuralCommitted));
    passed &= Check(
        structuralPresentation.CommitTo(structuralCommitted) &&
            structuralPresentation.GetPhase() ==
                StructuralAliasingPhase::Inactive &&
            aliasingLayoutSignature(
                structuralPresentation.
                    PresentStructuralBody(structuralCommitted)) ==
                structuralLayoutBeforeCommit,
        "structural commit resets its phase without a second Aliasing reflow");

    TestAliasingSettings hiddenCommitted{ 0, 1, 57 };
    StructuralAliasingPresentation hiddenPresentation;
    hiddenPresentation.Stage(
        hiddenCommitted,
        true,
        200,
        [](TestAliasingSettings& staged)
        {
            staged.method = 1;
        });
    hiddenPresentation.SkipInvisibleAnimation(200);
    passed &= Check(
        hiddenPresentation.GetPhase() ==
                StructuralAliasingPhase::ReadyToCommit &&
            hiddenPresentation.ReadyForCommit() &&
            hiddenPresentation.ShowStructuralBody() &&
            hiddenPresentation.
                PresentStructuralBody(hiddenCommitted).method == 1 &&
            hiddenCommitted.method == 0,
        "an invisible structural choice skips animation without applying renderer state");
    hiddenPresentation.Cancel();
    passed &= Check(
        !hiddenPresentation.HasPending() &&
            hiddenPresentation.GetPhase() ==
                StructuralAliasingPhase::Inactive &&
            hiddenPresentation.ReadyForCommit() &&
            hiddenPresentation.
                PresentStructuralBody(hiddenCommitted).method == 0,
        "canceling hidden staged work restores the committed presentation");

    TestAliasingSettings nonStructuralCommitted{ 0, 1, 63 };
    StructuralAliasingPresentation nonStructuralPresentation;
    nonStructuralPresentation.Stage(
        nonStructuralCommitted,
        false,
        300,
        [](TestAliasingSettings& staged)
        {
            staged.quality = 3;
        });
    passed &= Check(
        nonStructuralPresentation.GetPhase() ==
                StructuralAliasingPhase::ReadyToCommit &&
            nonStructuralPresentation.ShowStructuralBody() &&
            nonStructuralPresentation.
                PresentStructuralBody(nonStructuralCommitted).quality == 3 &&
            nonStructuralCommitted.quality == 1,
        "a nonstructural choice uses the staged body without a false collapse");

    passed &= Check(
        Near(
            ResolveUiExpandedMeasurement(
                120.f,
                0.f,
                false,
                true),
            120.f),
        "closing freezes the last expanded measurement");
    passed &= Check(
        Near(
            ResolveUiExpandedMeasurement(
                120.f,
                164.f,
                true,
                true),
            164.f),
        "a visible open body uses its submitted direct measurement");
    passed &= Check(
        Near(
            ResolveUiExpandedMeasurement(
                120.f,
                164.f,
                true,
                false),
            120.f),
        "an offscreen open body retains its last trustworthy measurement");

    if (!passed)
        return 1;

    std::cout << "UI animation reference validation passed\n";
    return 0;
}
