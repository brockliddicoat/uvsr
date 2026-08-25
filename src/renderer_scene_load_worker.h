#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <thread>

namespace uvsr
{
    enum class RendererSceneLoadWorkerState
    {
        Idle,
        Running,
        Succeeded,
        Failed
    };

    // Owns exactly one scene-load task. The terminal state is published with
    // release/acquire ordering, so the render thread may consume a task's
    // completed CPU handoff after observing Succeeded and before Join.
    class RendererSceneLoadWorker final
    {
    public:
        using Task = std::function<bool()>;

        RendererSceneLoadWorker() = default;
        ~RendererSceneLoadWorker();

        RendererSceneLoadWorker(const RendererSceneLoadWorker&) = delete;
        RendererSceneLoadWorker& operator=(
            const RendererSceneLoadWorker&) = delete;
        RendererSceneLoadWorker(RendererSceneLoadWorker&&) = delete;
        RendererSceneLoadWorker& operator=(
            RendererSceneLoadWorker&&) = delete;

        // Starts only when no prior task remains joinable. A terminal task may
        // be reused after Join without a separate reset.
        [[nodiscard]] bool Start(Task task);

        [[nodiscard]] RendererSceneLoadWorkerState GetState() const noexcept
        {
            return m_State.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return GetState() == RendererSceneLoadWorkerState::Running;
        }

        [[nodiscard]] bool IsFinished() const noexcept
        {
            const RendererSceneLoadWorkerState state = GetState();
            return state == RendererSceneLoadWorkerState::Succeeded ||
                state == RendererSceneLoadWorkerState::Failed;
        }

        [[nodiscard]] bool IsJoinable() const noexcept
        {
            return m_Thread.joinable();
        }

        // Blocks until completion when needed. Returns the task result.
        [[nodiscard]] bool Join();

        // Available after Failed is observed. A false task result has no
        // exception; thrown task exceptions are retained for diagnostics.
        [[nodiscard]] std::exception_ptr GetException() const noexcept;

        // Joins any task, clears its diagnostic, and returns to Idle.
        void Reset();

    private:
        std::thread m_Thread;
        std::atomic<RendererSceneLoadWorkerState> m_State{
            RendererSceneLoadWorkerState::Idle
        };
        std::exception_ptr m_Exception;
    };
}
