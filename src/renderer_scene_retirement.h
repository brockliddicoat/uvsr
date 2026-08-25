#pragma once

#include <nvrhi/nvrhi.h>

#include <functional>

namespace uvsr
{
    enum class RendererSceneRetirementStatus
    {
        Idle,
        Pending,
        Ready,
        Failed
    };

    enum class RendererSceneQueryStatus
    {
        Pending,
        Complete,
        Failed
    };

    struct RendererSceneRetirementOperations
    {
        std::function<bool()> armQuery;
        std::function<RendererSceneQueryStatus()> pollQuery;
        std::function<bool()> waitForIdle;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return bool(armQuery) && bool(pollQuery) && bool(waitForIdle);
        }
    };

    // Fences the graphics queue before the caller releases scene-owned GPU
    // resources. Begin merely arms the request; the first Poll signals the
    // queue so submissions made earlier in that render turn are included.
    class RendererSceneRetirement final
    {
    public:
        explicit RendererSceneRetirement(nvrhi::IDevice* device);
        explicit RendererSceneRetirement(
            RendererSceneRetirementOperations operations);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return bool(m_Operations);
        }

        [[nodiscard]] bool Begin() noexcept;

        [[nodiscard]] RendererSceneRetirementStatus Poll();

        // Completes the handoff after the caller has released its old scene
        // and reset dependent caches. Returns false unless Poll reported Ready.
        [[nodiscard]] bool Consume() noexcept;

        [[nodiscard]] bool UsedBlockingFallback() const noexcept
        {
            return m_UsedBlockingFallback;
        }

    private:
        enum class State
        {
            Idle,
            ArmQuery,
            WaitForQuery,
            Ready,
            Failed
        };

        RendererSceneRetirementOperations m_Operations;
        State m_State = State::Idle;
        bool m_UsedBlockingFallback = false;
    };
}
