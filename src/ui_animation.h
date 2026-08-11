#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace uvsr
{
    inline constexpr double UiDropdownSelectionSettleSeconds = 0.25;
    inline constexpr float UiDisabledPresentationDurationSeconds = 0.280f;

    // Advances only the normalized presentation timeline. Per-ID storage,
    // once-per-frame advancement, and submission-gap recovery belong to the
    // caller so this math stays deterministic and independent of ImGui state.
    [[nodiscard]] constexpr float AdvanceUiDisabledPresentation(
        float linearAmount,
        bool disabled,
        float deltaSeconds,
        bool motionEnabled)
    {
        const float target = disabled ? 1.f : 0.f;
        if (!motionEnabled)
            return target;

        const float amount = std::clamp(linearAmount, 0.f, 1.f);
        const float safeDeltaSeconds = std::clamp(
            deltaSeconds,
            0.f,
            1.f / 30.f);
        const float step =
            safeDeltaSeconds / UiDisabledPresentationDurationSeconds;
        return target > amount
            ? std::min(target, amount + step)
            : std::max(target, amount - step);
    }

    [[nodiscard]] constexpr float SmoothUiDisabledPresentation(
        float linearAmount)
    {
        const float amount = std::clamp(linearAmount, 0.f, 1.f);
        return amount * amount * (3.f - 2.f * amount);
    }

    [[nodiscard]] constexpr bool
        ShouldPlaceUiResetInNestedDropdownGutter(
            bool isDropdown,
            std::size_t nestedDepth)
    {
        return isDropdown && nestedDepth > 0u;
    }

    // Returns the signed horizontal offset from a nested dropdown's left edge
    // to the reset button's left edge. The button is centered in the nesting
    // indentation gutter, so the ordinary dropdown and its trailing label keep
    // their established geometry.
    [[nodiscard]] constexpr float ResolveNestedDropdownResetOffset(
        float indentSpacing,
        float buttonSize)
    {
        const float safeIndentSpacing = std::max(0.f, indentSpacing);
        const float safeButtonSize = std::max(0.f, buttonSize);
        return -safeIndentSpacing +
            (safeIndentSpacing - safeButtonSize) * 0.5f;
    }

    // Keep renderer-facing UI mutations keyed and ordered without coupling the
    // queue to ImGui or a particular callback type. Replacing one key preserves
    // its original position, while draining detaches every current entry before
    // the first callback so a callback may safely enqueue follow-up work.
    template <typename Key, typename Payload>
    class DeferredUiActionQueue
    {
    public:
        struct Entry
        {
            Key key;
            Payload payload;
        };

        [[nodiscard]] bool Empty() const
        {
            return m_Entries.empty();
        }

        [[nodiscard]] std::size_t Size() const
        {
            return m_Entries.size();
        }

        [[nodiscard]] const Payload* Find(const Key& key) const
        {
            const auto entry = FindEntry(key);
            return entry != m_Entries.end()
                ? &entry->payload
                : nullptr;
        }

        template <typename PendingPayload>
        void Upsert(const Key& key, PendingPayload&& payload)
        {
            const auto entry = FindEntry(key);
            if (entry != m_Entries.end())
            {
                entry->payload =
                    std::forward<PendingPayload>(payload);
                return;
            }

            m_Entries.push_back({
                key,
                std::forward<PendingPayload>(payload)
            });
        }

        template <typename Callback>
        bool Drain(Callback&& callback)
        {
            if (m_Entries.empty())
                return false;

            std::vector<Entry> pending = std::move(m_Entries);
            m_Entries.clear();
            for (Entry& entry : pending)
            {
                callback(
                    entry.key,
                    std::move(entry.payload));
            }
            return true;
        }

        void Clear()
        {
            m_Entries.clear();
        }

    private:
        [[nodiscard]] auto FindEntry(const Key& key)
        {
            return std::find_if(
                m_Entries.begin(),
                m_Entries.end(),
                [&key](const Entry& candidate)
                {
                    return candidate.key == key;
                });
        }

        [[nodiscard]] auto FindEntry(const Key& key) const
        {
            return std::find_if(
                m_Entries.begin(),
                m_Entries.end(),
                [&key](const Entry& candidate)
                {
                    return candidate.key == key;
                });
        }

        std::vector<Entry> m_Entries;
    };

    // Hold a UI-only snapshot while an expensive renderer mutation is waiting
    // for its commit barrier. Layout reads the staged value immediately, but
    // the committed value stays untouched. Committing copies the same snapshot
    // into the renderer-facing value and removes the shadow, so presentation is
    // identical on both sides of the commit and cannot reflow a second time.
    template <typename Value>
    class DeferredUiPresentation
    {
    public:
        [[nodiscard]] bool HasPending() const
        {
            return m_Pending.has_value();
        }

        [[nodiscard]] const Value& Present(
            const Value& committed) const
        {
            return m_Pending
                ? *m_Pending
                : committed;
        }

        [[nodiscard]] Value& Present(Value& committed)
        {
            return m_Pending
                ? *m_Pending
                : committed;
        }

        template <typename Mutator>
        Value& Stage(
            const Value& committed,
            Mutator&& mutator)
        {
            if (!m_Pending)
                m_Pending.emplace(committed);
            mutator(*m_Pending);
            return *m_Pending;
        }

        bool CommitTo(Value& committed)
        {
            if (!m_Pending)
                return false;

            committed = std::move(*m_Pending);
            m_Pending.reset();
            return true;
        }

        void Cancel()
        {
            m_Pending.reset();
        }

    private:
        std::optional<Value> m_Pending;
    };

    struct UiDrawerHeightDeltas
    {
        float total = 0.f;
        float aboveViewport = 0.f;
    };

    // Track the layout movement caused by top-level drawer bodies independently
    // from the subset that can safely anchor the current viewport. A changing
    // body is wholly above the viewport only when the larger of its old and new
    // displayed envelopes ends above the viewport boundary. Using the larger
    // envelope makes both expansion and collapse conservative and prevents an
    // intersecting row from being treated as already scrolled past.
    [[nodiscard]] constexpr UiDrawerHeightDeltas
        AccumulateUiDrawerHeightDelta(
            UiDrawerHeightDeltas accumulated,
            float bodyTop,
            float previousDisplayedHeight,
            float currentDisplayedHeight,
            float viewportTop)
    {
        const float heightDelta =
            currentDisplayedHeight - previousDisplayedHeight;
        accumulated.total += heightDelta;

        const float largestDisplayedHeight =
            previousDisplayedHeight > currentDisplayedHeight
                ? previousDisplayedHeight
                : currentDisplayedHeight;
        if (bodyTop + largestDisplayedHeight <= viewportTop + 0.5f)
            accumulated.aboveViewport += heightDelta;

        return accumulated;
    }

    // A viewport already pinned to the bottom follows every content-height
    // change. Otherwise, compensate only for complete bodies above the visible
    // area so an intersecting animation remains visible instead of being
    // scrolled away by its own height change.
    [[nodiscard]] constexpr float ResolveUiScrollAnchorDelta(
        const UiDrawerHeightDeltas& deltas,
        bool preserveBottom)
    {
        return preserveBottom
            ? deltas.total
            : deltas.aboveViewport;
    }

    struct UiScrollAnchorCorrection
    {
        bool apply = false;
        float scrollY = 0.f;
    };

    // ImGui consumes wheel targets when the child begins. Apply the resulting
    // layout correction directly to that settled scroll position so the next
    // wheel event cannot overwrite a second, delayed target. An independently
    // pending navigation/programmatic target owns the frame and is left intact.
    [[nodiscard]] constexpr UiScrollAnchorCorrection
        ResolveUiScrollAnchorCorrection(
            float currentScrollY,
            float scrollDelta,
            float currentFrameScrollMaxY,
            bool hasPendingScrollTarget,
            float wheelInput = 0.f,
            bool wheelAtTop = false,
            bool wheelAtBottom = false)
    {
        if (hasPendingScrollTarget)
        {
            return { false, currentScrollY };
        }

        const float maximumScrollY =
            currentFrameScrollMaxY > 0.f
                ? currentFrameScrollMaxY
                : 0.f;
        if (wheelInput > 0.001f && wheelAtTop)
        {
            return {
                currentScrollY > 0.01f,
                0.f
            };
        }
        if (wheelInput < -0.001f && wheelAtBottom)
        {
            const float endpointDifference =
                currentScrollY - maximumScrollY;
            return {
                endpointDifference > 0.01f ||
                    endpointDifference < -0.01f,
                maximumScrollY
            };
        }
        if (scrollDelta >= -0.01f && scrollDelta <= 0.01f)
            return { false, currentScrollY };

        const float requestedScrollY = currentScrollY + scrollDelta;
        return {
            true,
            requestedScrollY < 0.f
                ? 0.f
                : requestedScrollY > maximumScrollY
                    ? maximumScrollY
                    : requestedScrollY
        };
    }

    // Retain the Settings viewport only when scroll state needs protection.
    // Layout animation by itself must not pin the old height: at the top of the
    // panel, AutoResizeY should follow the already-animated drawer envelopes so
    // the outer surface contracts continuously instead of snapping afterward.
    [[nodiscard]] constexpr bool ShouldRetainUiViewportHeight(
        bool hasScrollOffset,
        bool hasWheelInput,
        bool isDraggingScrollbar)
    {
        return hasScrollOffset ||
            hasWheelInput ||
            isDraggingScrollbar;
    }

    // Dropdown selections may synchronously rebuild render targets, allocate
    // visibility resources, or create a first-use pipeline on the following
    // renderer frame. Arm the commit only after the complete UI composition is
    // idle, then preserve one whole idle frame for presentation before the
    // renderer-facing mutation is allowed to run.
    [[nodiscard]] constexpr int UpdateUiDropdownIdleStartFrame(
        int idleStartFrame,
        int currentFrame,
        bool compositionIdle)
    {
        if (!compositionIdle)
            return -1;
        return idleStartFrame >= 0
            ? idleStartFrame
            : currentFrame;
    }

    [[nodiscard]] constexpr bool ShouldCommitDeferredDropdownActions(
        int currentFrame,
        int requestFrame,
        int idleStartFrame,
        double secondsSinceLastRequest)
    {
        return requestFrame >= 0 &&
            currentFrame > requestFrame &&
            idleStartFrame >= 0 &&
            currentFrame > idleStartFrame &&
            secondsSinceLastRequest >= UiDropdownSelectionSettleSeconds;
    }

    struct UiExpandedMeasurementState
    {
        float height = 0.f;
        bool valid = false;
    };

    // A control that was not submitted for one or more frames must not infer
    // motion from stale elapsed time. Preserve an unchanged open endpoint, but
    // restart a newly opened or not-yet-measured body from the closed endpoint.
    [[nodiscard]] constexpr float ResolveUiOpenAmountAfterSubmissionGap(
        bool targetOpen,
        bool previousTargetOpen,
        bool needsInitialMeasurement)
    {
        return targetOpen &&
                previousTargetOpen &&
                !needsInitialMeasurement
            ? 1.f
            : 0.f;
    }

    [[nodiscard]] constexpr bool NeedsInitialUiExpandedMeasurement(
        bool targetOpen,
        const UiExpandedMeasurementState& state)
    {
        return targetOpen && !state.valid;
    }

    // The expanded measurement is authoritative only while the region is both
    // logically open and actually submitted. Closing must retain the last open
    // measurement so a changing child cannot deform the exit envelope. An open
    // but clipped/offscreen body likewise keeps its cache until it is visible
    // and can provide a trustworthy direct measurement.
    [[nodiscard]] constexpr float ResolveUiExpandedMeasurement(
        float cachedExpandedHeight,
        float submittedExpandedHeight,
        bool targetOpen,
        bool bodyVisible)
    {
        return targetOpen && bodyVisible
            ? submittedExpandedHeight
            : cachedExpandedHeight;
    }

    [[nodiscard]] constexpr UiExpandedMeasurementState
        SubmitUiExpandedMeasurement(
            UiExpandedMeasurementState state,
            float submittedExpandedHeight,
            bool targetOpen,
            bool bodyVisible)
    {
        state.height = ResolveUiExpandedMeasurement(
            state.height,
            submittedExpandedHeight,
            targetOpen,
            bodyVisible);
        if (targetOpen && bodyVisible)
            state.valid = true;
        return state;
    }
}
