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

    const UiScrollAnchorCorrection fastWheelBottom =
        ResolveUiScrollAnchorCorrection(
            900.f,
            80.f,
            980.f,
            false);
    passed &= Check(
        fastWheelBottom.apply &&
            Near(fastWheelBottom.scrollY, 980.f),
        "a consumed fast-wheel target composes once with bottom growth");

    const UiScrollAnchorCorrection upperClamp =
        ResolveUiScrollAnchorCorrection(
            970.f,
            80.f,
            1000.f,
            false);
    passed &= Check(
        upperClamp.apply &&
            Near(upperClamp.scrollY, 1000.f),
        "anchor growth clamps at the current-frame scroll maximum");

    const UiScrollAnchorCorrection shrinkingMaximum =
        ResolveUiScrollAnchorCorrection(
            980.f,
            -60.f,
            920.f,
            false);
    passed &= Check(
        shrinkingMaximum.apply &&
            Near(shrinkingMaximum.scrollY, 920.f),
        "content shrink clamps a formerly valid bottom position");

    const UiScrollAnchorCorrection ordinaryAnchor =
        ResolveUiScrollAnchorCorrection(
            400.f,
            35.f,
            1000.f,
            false);
    passed &= Check(
        ordinaryAnchor.apply &&
            Near(ordinaryAnchor.scrollY, 435.f),
        "ordinary expansion preserves its visible anchor");

    const UiScrollAnchorCorrection collapsingAnchor =
        ResolveUiScrollAnchorCorrection(
            400.f,
            -35.f,
            1000.f,
            false);
    passed &= Check(
        collapsingAnchor.apply &&
            Near(collapsingAnchor.scrollY, 365.f),
        "ordinary collapse preserves its visible anchor");

    const UiScrollAnchorCorrection pendingTarget =
        ResolveUiScrollAnchorCorrection(
            400.f,
            35.f,
            1000.f,
            true);
    passed &= Check(
        !pendingTarget.apply &&
            Near(pendingTarget.scrollY, 400.f),
        "an independently pending ImGui target owns the frame");

    const UiScrollAnchorCorrection settledFrame =
        ResolveUiScrollAnchorCorrection(
            fastWheelBottom.scrollY,
            0.f,
            980.f,
            false);
    passed &= Check(
        !settledFrame.apply &&
            Near(settledFrame.scrollY, 980.f),
        "a consumed anchor delta is not applied twice");

    const UiScrollAnchorCorrection outwardTopLock =
        ResolveUiScrollAnchorCorrection(
            18.f,
            42.f,
            980.f,
            false,
            1.f,
            true,
            false);
    passed &= Check(
        outwardTopLock.apply && Near(outwardTopLock.scrollY, 0.f),
        "outward wheel input at the top wins over drawer anchor growth");
    const UiScrollAnchorCorrection repeatedTopLock =
        ResolveUiScrollAnchorCorrection(
            outwardTopLock.scrollY,
            42.f,
            980.f,
            false,
            1.f,
            true,
            false);
    passed &= Check(
        !repeatedTopLock.apply && Near(repeatedTopLock.scrollY, 0.f),
        "the top endpoint lock is idempotent on repeated frames");

    const UiScrollAnchorCorrection outwardBottomLock =
        ResolveUiScrollAnchorCorrection(
            940.f,
            -42.f,
            920.f,
            false,
            -1.f,
            false,
            true);
    passed &= Check(
        outwardBottomLock.apply && Near(outwardBottomLock.scrollY, 920.f),
        "outward wheel input at the bottom follows the current-frame maximum");
    const UiScrollAnchorCorrection growingBottomLock =
        ResolveUiScrollAnchorCorrection(
            outwardBottomLock.scrollY,
            -42.f,
            1040.f,
            false,
            -1.f,
            false,
            true);
    passed &= Check(
        growingBottomLock.apply && Near(growingBottomLock.scrollY, 1040.f),
        "a held bottom endpoint follows a changed current-frame maximum");
    const UiScrollAnchorCorrection repeatedBottomLock =
        ResolveUiScrollAnchorCorrection(
            growingBottomLock.scrollY,
            -42.f,
            1040.f,
            false,
            -1.f,
            false,
            true);
    passed &= Check(
        !repeatedBottomLock.apply &&
            Near(repeatedBottomLock.scrollY, 1040.f),
        "the bottom endpoint lock is idempotent once the new maximum settles");

    const UiScrollAnchorCorrection pendingTargetAtEndpoint =
        ResolveUiScrollAnchorCorrection(
            23.f,
            42.f,
            980.f,
            true,
            1.f,
            true,
            false);
    passed &= Check(
        !pendingTargetAtEndpoint.apply &&
            Near(pendingTargetAtEndpoint.scrollY, 23.f),
        "a pending ImGui target takes precedence over endpoint wheel locks");

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

    passed &= Check(
        UiDropdownSelectionSettleSeconds == 0.25,
        "deferred dropdown selections retain the established quarter-second "
        "settle interval");
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

    // An authored selection keeps composition non-idle for its retained
    // roll-up even after the quarter-second request timer has elapsed. Once
    // that exact popup transition disappears, one complete idle frame must be
    // presented before the renderer-facing callback may run.
    constexpr int authoredSelectionRequestFrame = 70;
    int authoredPopupIdleStart = -1;
    authoredPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        authoredPopupIdleStart,
        authoredSelectionRequestFrame,
        false);
    passed &= Check(
        authoredPopupIdleStart == -1 &&
            !ShouldCommitDeferredDropdownActions(
                authoredSelectionRequestFrame,
                authoredSelectionRequestFrame,
                authoredPopupIdleStart,
                UiDropdownSelectionSettleSeconds),
        "an authored popup roll cannot commit on its selection frame");
    authoredPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        authoredPopupIdleStart,
        authoredSelectionRequestFrame + 12,
        false);
    passed &= Check(
        authoredPopupIdleStart == -1 &&
            !ShouldCommitDeferredDropdownActions(
                authoredSelectionRequestFrame + 12,
                authoredSelectionRequestFrame,
                authoredPopupIdleStart,
                UiDropdownSelectionSettleSeconds + 0.05),
        "an active roll-up keeps the deferred action blocked even after the "
        "quarter-second request timer has elapsed");
    authoredPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        authoredPopupIdleStart,
        authoredSelectionRequestFrame + 13,
        true);
    passed &= Check(
        authoredPopupIdleStart == authoredSelectionRequestFrame + 13 &&
            !ShouldCommitDeferredDropdownActions(
                authoredSelectionRequestFrame + 13,
                authoredSelectionRequestFrame,
                authoredPopupIdleStart,
                UiDropdownSelectionSettleSeconds),
        "the first fully idle frame after roll-up only arms the idle barrier");
    authoredPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        authoredPopupIdleStart,
        authoredSelectionRequestFrame + 14,
        true);
    passed &= Check(
        ShouldCommitDeferredDropdownActions(
            authoredSelectionRequestFrame + 14,
            authoredSelectionRequestFrame,
            authoredPopupIdleStart,
            UiDropdownSelectionSettleSeconds),
        "an authored choice commits only after roll-up, the full settle time, "
        "and one presented idle frame");

    constexpr int oggSelectionRequestFrame = 90;
    int oggPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        -1,
        oggSelectionRequestFrame,
        false);
    oggPopupIdleStart = UpdateUiDropdownIdleStartFrame(
        oggPopupIdleStart,
        oggSelectionRequestFrame + 1,
        true);
    passed &= Check(
        oggPopupIdleStart == oggSelectionRequestFrame + 1 &&
            !ShouldCommitDeferredDropdownActions(
                oggSelectionRequestFrame + 1,
                oggSelectionRequestFrame,
                oggPopupIdleStart,
                UiDropdownSelectionSettleSeconds),
        "Ogg's immediate stock popup close still presents an idle endpoint "
        "before commit");
    passed &= Check(
        ShouldCommitDeferredDropdownActions(
            oggSelectionRequestFrame + 2,
            oggSelectionRequestFrame,
            oggPopupIdleStart,
            UiDropdownSelectionSettleSeconds),
        "Ogg shares the unchanged quarter-second and later-idle-frame commit "
        "barrier despite having no popup roll");

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

    passed &= Check(
        Near(
            ResolveUiOpenAmountAfterSubmissionGap(
                false,
                true,
                false),
            0.f) &&
            Near(
                ResolveUiOpenAmountAfterSubmissionGap(
                    true,
                    false,
                    false),
                0.f),
        "a skipped drawer submission must restart a close or false-to-true "
        "open transition at the closed endpoint");
    passed &= Check(
        Near(
            ResolveUiOpenAmountAfterSubmissionGap(
                true,
                true,
                true),
            0.f) &&
            Near(
                ResolveUiOpenAmountAfterSubmissionGap(
                    true,
                    true,
                    false),
                1.f),
        "a skipped submitted drawer preserves only a measured unchanged-open "
        "endpoint");

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

    UiExpandedMeasurementState emptyMeasurement;
    passed &= Check(
        NeedsInitialUiExpandedMeasurement(true, emptyMeasurement) &&
            !NeedsInitialUiExpandedMeasurement(false, emptyMeasurement),
        "only a visible unmeasured toggle region requests initial layout");
    emptyMeasurement = SubmitUiExpandedMeasurement(
        emptyMeasurement,
        0.f,
        true,
        true);
    passed &= Check(
        emptyMeasurement.valid &&
            Near(emptyMeasurement.height, 0.f) &&
            !NeedsInitialUiExpandedMeasurement(true, emptyMeasurement),
        "a submitted zero-height body is a completed measurement");
    const UiExpandedMeasurementState skippedMeasurement =
        SubmitUiExpandedMeasurement(
            UiExpandedMeasurementState{},
            64.f,
            true,
            false);
    passed &= Check(
        !skippedMeasurement.valid &&
            Near(skippedMeasurement.height, 0.f) &&
            NeedsInitialUiExpandedMeasurement(
                true,
                skippedMeasurement),
        "an offscreen body cannot complete initial measurement");

    static_assert(
        UiDisabledPresentationDurationSeconds == 0.280f,
        "the authored disabled transition lasts 280 ms");
    static_assert(
        SmoothUiDisabledPresentation(0.5f) == 0.5f,
        "the disabled easing helper remains constexpr");

    passed &= Check(
        Near(
            AdvanceUiDisabledPresentation(0.f, false, 1.f / 60.f, true),
            0.f) &&
            Near(
                AdvanceUiDisabledPresentation(1.f, true, 1.f / 60.f, true),
                1.f) &&
            Near(
                AdvanceUiDisabledPresentation(0.99f, true, 1.f / 30.f, true),
                1.f) &&
            Near(
                AdvanceUiDisabledPresentation(0.01f, false, 1.f / 30.f, true),
                0.f),
        "disabled presentation stays at and snaps exactly to its endpoints");

    const auto advanceDisabledFor = [](
        float deltaSeconds,
        int frameCount)
    {
        float amount = 0.f;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            amount = AdvanceUiDisabledPresentation(
                amount,
                true,
                deltaSeconds,
                true);
        }
        return amount;
    };
    const float disabledAtThirtyHz = advanceDisabledFor(1.f / 30.f, 6);
    const float disabledAtSixtyHz = advanceDisabledFor(1.f / 60.f, 12);
    const float disabledAtOneTwentyHz = advanceDisabledFor(1.f / 120.f, 24);
    passed &= Check(
        Near(disabledAtThirtyHz, 0.2f / 0.280f) &&
            Near(disabledAtThirtyHz, disabledAtSixtyHz) &&
            Near(disabledAtSixtyHz, disabledAtOneTwentyHz),
        "30, 60, and 120 Hz advance equally for the same elapsed time");

    const float maximumStep = AdvanceUiDisabledPresentation(
        0.f,
        true,
        1.f / 30.f,
        true);
    passed &= Check(
        Near(
            AdvanceUiDisabledPresentation(0.f, true, 1.f, true),
            maximumStep) &&
            Near(
                AdvanceUiDisabledPresentation(0.4f, true, -1.f, true),
                0.4f),
        "disabled presentation clamps frame time to [0, 1/30] seconds");

    constexpr float reversalStart = 0.6f;
    const float reversingTowardEnabled = AdvanceUiDisabledPresentation(
        reversalStart,
        false,
        1.f / 120.f,
        true);
    const float reversedTowardDisabled = AdvanceUiDisabledPresentation(
        reversingTowardEnabled,
        true,
        1.f / 120.f,
        true);
    passed &= Check(
        reversingTowardEnabled > 0.f &&
            reversingTowardEnabled < reversalStart &&
            Near(reversedTowardDisabled, reversalStart),
        "reversing a disabled transition continues from its current amount");

    passed &= Check(
        Near(
            AdvanceUiDisabledPresentation(0.25f, true, 0.f, false),
            1.f) &&
            Near(
                AdvanceUiDisabledPresentation(0.75f, false, 0.f, false),
                0.f),
        "motion-off disabled presentation snaps immediately to its target");

    passed &= Check(
        Near(SmoothUiDisabledPresentation(-1.f), 0.f) &&
            Near(SmoothUiDisabledPresentation(0.f), 0.f) &&
            Near(SmoothUiDisabledPresentation(0.25f), 0.15625f) &&
            Near(SmoothUiDisabledPresentation(0.5f), 0.5f) &&
            Near(SmoothUiDisabledPresentation(0.75f), 0.84375f) &&
            Near(SmoothUiDisabledPresentation(1.f), 1.f) &&
            Near(SmoothUiDisabledPresentation(2.f), 1.f),
        "disabled presentation smoothstep is clamped and endpoint-stable");

    if (!passed)
        return 1;

    std::cout << "UI animation reference validation passed\n";
    return 0;
}
