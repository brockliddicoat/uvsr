#pragma once

#include <stdint.h>

namespace uvsr
{
    enum class RendererSceneRetirementStatus : uint8_t
    {
        Idle,
        Pending,
        Ready,
        Failed
    };

    enum class RendererSceneQueryStatus : uint8_t
    {
        Pending,
        Complete,
        Failed
    };

    struct RendererSceneRetirementOperations
    {
        // borrowed until the gate is destroyed. callbacks execute synchronously;
        // their context must outlive the gate and must not move while retained.
        void* context = nullptr;
        bool (*armQuery)(void*) = nullptr;
        RendererSceneQueryStatus (*pollQuery)(void*) = nullptr;
        bool (*waitForIdle)(void*) = nullptr;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return armQuery && pollQuery && waitForIdle;
        }
    };

    // Fences the graphics queue before the caller releases scene-owned GPU
    // resources. Begin merely arms the request; the first Poll signals the
    // queue so submissions made earlier in that render turn are included.
    class RendererSceneRetirement final
    {
    public:
        explicit RendererSceneRetirement(
            RendererSceneRetirementOperations operations = {});
        RendererSceneRetirement(const RendererSceneRetirement&) = delete;
        RendererSceneRetirement& operator=(const RendererSceneRetirement&) = delete;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return bool(m_Operations);
        }

        [[nodiscard]] bool Begin() noexcept;

        [[nodiscard]] RendererSceneRetirementStatus Poll();

        // Proves queue idleness when asynchronous retirement cannot finish,
        // including during application shutdown.
        [[nodiscard]] RendererSceneRetirementStatus CompleteBlocking();

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
