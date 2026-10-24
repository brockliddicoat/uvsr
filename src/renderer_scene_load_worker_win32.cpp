#include "renderer_scene_load_worker.h"

#include <Windows.h>
#include <errno.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_CPPUNWIND)
// temporary boundary while the Donut importer can throw, removed in stage 4.
#include <exception>
#endif

namespace uvsr
{
    namespace
    {
        using State = RendererSceneLoadWorkerState;
        static_assert(sizeof(long) == 4, "Win32 Interlocked state requires a 32-bit long");
        [[noreturn]] void FailWorkerLifetime(const char* operation) noexcept
        {
            fprintf(stderr, "scene worker could not prove %s, Win32 error %lu\n", operation, GetLastError());
            abort();
        }
    }

    struct RendererSceneLoadWorkerWin32
    {
        static unsigned __stdcall Run(void* context) noexcept
        {
            auto& self = *static_cast<RendererSceneLoadWorker*>(context);
            const RendererSceneLoadCancellation cancellation(self);
            bool succeeded = false;
            bool exception = false;
#if defined(_CPPUNWIND)
            try
            {
#endif
                succeeded = self.m_Task(self.m_Context, cancellation);
#if defined(_CPPUNWIND)
            }
            catch (const std::exception& error)
            {
                snprintf(self.m_Failure, sizeof(self.m_Failure), "%s", error.what());
                exception = true;
            }
            catch (...)
            {
                snprintf(self.m_Failure, sizeof(self.m_Failure), "%s",
                    "The scene importer threw an unknown exception.");
                exception = true;
            }
#endif
            const bool reportedFailure = self.m_Failure[0] != '\0';
            const State completed = succeeded && !reportedFailure ? State::Succeeded : State::Failed;
            const long previous = InterlockedCompareExchange(&self.m_State,
                static_cast<long>(completed), static_cast<long>(State::Running));
            if (previous == static_cast<long>(State::CancelRequested))
                InterlockedExchange(&self.m_State, static_cast<long>(exception || reportedFailure ? State::Failed : State::Cancelled));
            else if (previous != static_cast<long>(State::Running))
                FailWorkerLifetime("task completion state");
            return 0;
        }
    };

    RendererSceneLoadWorker::~RendererSceneLoadWorker() { Reset(); }

    bool RendererSceneLoadWorker::Start(Task task, void* context) noexcept
    {
        if (!task || m_Thread)
            return false;
        m_Task = task;
        m_Context = context;
        m_Failure[0] = '\0';
        InterlockedExchange(&m_State, static_cast<long>(State::Running));
        const uintptr_t thread = _beginthreadex(nullptr, 0, RendererSceneLoadWorkerWin32::Run, this, 0, nullptr);
        if (thread == 0)
        {
            const int failure = errno;
            m_Task = nullptr;
            m_Context = nullptr;
            snprintf(m_Failure, sizeof(m_Failure), "Scene worker creation failed, errno %d", failure);
            InterlockedExchange(&m_State, static_cast<long>(State::Failed));
            return false;
        }
        m_Thread = reinterpret_cast<void*>(thread);
        return true;
    }

    RendererSceneLoadWorkerState RendererSceneLoadWorker::GetState() const noexcept
    {
        return static_cast<State>(InterlockedCompareExchange(&m_State, 0, 0));
    }

    bool RendererSceneLoadCancellation::IsRequested() const noexcept
    {
        return m_Worker.GetState() == State::CancelRequested;
    }

    bool RendererSceneLoadCancellation::Fail(const char* text) const noexcept
    {
        snprintf(m_Worker.m_Failure, sizeof(m_Worker.m_Failure), "%s",
            text && text[0] ? text : "The scene task reported a failure.");
        return false;
    }

    void RendererSceneLoadWorker::RequestCancel() noexcept
    {
        InterlockedCompareExchange(&m_State, static_cast<long>(State::CancelRequested), static_cast<long>(State::Running));
    }

    bool RendererSceneLoadWorker::Join() noexcept
    {
        if (m_Thread)
        {
            if (GetThreadId(m_Thread) == GetCurrentThreadId() ||
                WaitForSingleObject(m_Thread, INFINITE) != WAIT_OBJECT_0)
                FailWorkerLifetime("thread completion");
            if (!CloseHandle(m_Thread))
                FailWorkerLifetime("thread handle release");
            m_Thread = nullptr;
            m_Task = nullptr;
            m_Context = nullptr;
        }
        return GetState() == State::Succeeded;
    }

    const char* RendererSceneLoadWorker::GetFailureText() const noexcept
    {
        return GetState() == State::Failed ? m_Failure : "";
    }

    void RendererSceneLoadWorker::Reset() noexcept
    {
        RequestCancel();
        (void)Join();
        m_Failure[0] = '\0';
        InterlockedExchange(&m_State, static_cast<long>(State::Idle));
    }
}
