#pragma once

namespace uvsr
{
    // keep the existing worker diagnostic limit. copies own their bytes and
    // remain readable after the worker resets, without allocating on failure.
    class RendererSceneLoadFailureText
    {
    public:
        static constexpr unsigned Capacity = 512;
        [[nodiscard]] const char* Data() const noexcept { return m_Text; }
        [[nodiscard]] bool Empty() const noexcept { return m_Text[0] == '\0'; }
        void Clear() noexcept { m_Text[0] = '\0'; }
        void Assign(const char* text) noexcept
        {
            unsigned length = 0;
            if (text)
                while (length + 1 < Capacity && text[length])
                { m_Text[length] = text[length]; ++length; }
            m_Text[length] = '\0';
        }
    private:
        char m_Text[Capacity]{};
    };

    enum class RendererSceneLoadWorkerState
    {
        Idle,
        Running,
        CancelRequested,
        Succeeded,
        Failed,
        Cancelled
    };

    class RendererSceneLoadWorker;
    struct RendererSceneLoadWorkerWin32;

    // borrowed only for one Task call. neither the token nor its worker may be
    // retained by child work after that call returns.
    class RendererSceneLoadCancellation final
    {
    public:
        [[nodiscard]] bool IsRequested() const noexcept;
        // task-only: copy the diagnostic now, publish Failed after the task
        // returns. explicit failure wins cancellation, as an exception does.
        [[nodiscard]] bool Fail(const char* text) const noexcept;
        RendererSceneLoadCancellation(const RendererSceneLoadCancellation&) = delete;
        RendererSceneLoadCancellation& operator=(const RendererSceneLoadCancellation&) = delete;
    private:
        friend struct RendererSceneLoadWorkerWin32;
        explicit RendererSceneLoadCancellation(RendererSceneLoadWorker& worker) : m_Worker(worker) {}
        RendererSceneLoadWorker& m_Worker;
    };

    // one owner calls Start, Join and Reset. the named context remains alive
    // until Join; readiness alone does not transfer its ownership. state reads
    // and RequestCancel synchronize with the task through the native backend.
    class RendererSceneLoadWorker final
    {
    public:
        using Task = bool (*)(void*, const RendererSceneLoadCancellation&);

        RendererSceneLoadWorker() = default;
        ~RendererSceneLoadWorker();

        RendererSceneLoadWorker(const RendererSceneLoadWorker&) = delete;
        RendererSceneLoadWorker& operator=(
            const RendererSceneLoadWorker&) = delete;
        RendererSceneLoadWorker(RendererSceneLoadWorker&&) = delete;
        RendererSceneLoadWorker& operator=(
            RendererSceneLoadWorker&&) = delete;

        [[nodiscard]] bool Start(Task task, void* context) noexcept;
        [[nodiscard]] RendererSceneLoadWorkerState GetState() const noexcept;
        void RequestCancel() noexcept;
        [[nodiscard]] bool Join() noexcept;
        // copied explicit/exception diagnostic, empty for plain false. valid until Reset
        // or the next Start. only read after Failed, on the owning thread.
        [[nodiscard]] const char* GetFailureText() const noexcept;
        void Reset() noexcept;

    private:
        friend struct RendererSceneLoadWorkerWin32;
        friend class RendererSceneLoadCancellation;
        void* m_Thread = nullptr;
        Task m_Task = nullptr;
        void* m_Context = nullptr;
        alignas(4) mutable volatile long m_State = 0;
        char m_Failure[RendererSceneLoadFailureText::Capacity]{};
    };
}
