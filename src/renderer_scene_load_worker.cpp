#include "renderer_scene_load_worker.h"

#include <utility>

namespace uvsr
{
    RendererSceneLoadWorker::~RendererSceneLoadWorker()
    {
        if (m_Thread.joinable())
            m_Thread.join();
    }

    bool RendererSceneLoadWorker::Start(Task task)
    {
        if (!task || m_Thread.joinable())
            return false;

        m_Exception = nullptr;
        m_State.store(
            RendererSceneLoadWorkerState::Running,
            std::memory_order_release);
        try
        {
            m_Thread = std::thread(
                [this, task = std::move(task)]() mutable
                {
                    bool succeeded = false;
                    try
                    {
                        succeeded = task();
                    }
                    catch (...)
                    {
                        m_Exception = std::current_exception();
                    }
                    m_State.store(
                        succeeded
                            ? RendererSceneLoadWorkerState::Succeeded
                            : RendererSceneLoadWorkerState::Failed,
                        std::memory_order_release);
                });
        }
        catch (...)
        {
            m_Exception = std::current_exception();
            m_State.store(
                RendererSceneLoadWorkerState::Failed,
                std::memory_order_release);
            return false;
        }
        return true;
    }

    bool RendererSceneLoadWorker::Join()
    {
        if (m_Thread.joinable())
            m_Thread.join();
        return GetState() == RendererSceneLoadWorkerState::Succeeded;
    }

    std::exception_ptr RendererSceneLoadWorker::GetException() const noexcept
    {
        return GetState() == RendererSceneLoadWorkerState::Failed
            ? m_Exception
            : nullptr;
    }

    void RendererSceneLoadWorker::Reset()
    {
        if (m_Thread.joinable())
            m_Thread.join();
        m_Exception = nullptr;
        m_State.store(
            RendererSceneLoadWorkerState::Idle,
            std::memory_order_release);
    }
}
