#pragma once

#include <cstdint>
#include <utility>

namespace uvsr
{
    inline constexpr uint32_t RendererShellWaitTimeoutMilliseconds = 30'000u;
    inline constexpr uint32_t RendererShellWaitSucceeded = 0u;
    inline constexpr uint32_t RendererShellWaitFailed = 0xffffffffu;

    enum class RendererShellOperation : uint8_t
    {
        None,
        BeginFrameWait,
        MessageLoopDeviceIdle,
        ShutdownDeviceIdle,
        ShutdownFrameWait,
        Present,
        ResetFenceEvent,
        SetFenceEvent,
        SignalFence,
        ResizeBuffers,
        CreateRenderTarget,
        RequiredRenderPass
    };

    struct RendererShellOperationResult
    {
        RendererShellOperation operation = RendererShellOperation::None;
        uint32_t nativeResult = 0u;
        uint32_t nativeDetail = 0u;

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return operation == RendererShellOperation::None;
        }
    };

    [[nodiscard]] constexpr const char* RendererShellOperationName(
        RendererShellOperation operation) noexcept
    {
        switch (operation)
        {
        case RendererShellOperation::BeginFrameWait:
            return "begin-frame fence wait";
        case RendererShellOperation::MessageLoopDeviceIdle:
            return "message-loop device idle wait";
        case RendererShellOperation::ShutdownDeviceIdle:
            return "shutdown device idle wait";
        case RendererShellOperation::ShutdownFrameWait:
            return "shutdown frame-fence wait";
        case RendererShellOperation::Present:
            return "swap-chain Present";
        case RendererShellOperation::ResetFenceEvent:
            return "frame-fence ResetEvent";
        case RendererShellOperation::SetFenceEvent:
            return "frame-fence SetEventOnCompletion";
        case RendererShellOperation::SignalFence:
            return "graphics-queue Signal";
        case RendererShellOperation::ResizeBuffers:
            return "swap-chain ResizeBuffers";
        case RendererShellOperation::CreateRenderTarget:
            return "swap-chain render-target creation";
        case RendererShellOperation::RequiredRenderPass:
            return "required renderer pass";
        default:
            return "none";
        }
    }

    class RendererShellFailureLatch
    {
    public:
        [[nodiscard]] bool Record(
            RendererShellOperationResult result) noexcept
        {
            if (result.Succeeded() || Failed())
                return false;
            m_Failure = result;
            return true;
        }

        [[nodiscard]] constexpr bool Failed() const noexcept
        {
            return !m_Failure.Succeeded();
        }

        [[nodiscard]] constexpr RendererShellOperationResult Failure()
            const noexcept
        {
            return m_Failure;
        }

    private:
        RendererShellOperationResult m_Failure;
    };

    class RendererShellTerminalDiagnosticLatch
    {
    public:
        [[nodiscard]] bool ShouldReport(bool terminal) noexcept
        {
            if (!terminal || m_Reported)
                return false;
            m_Reported = true;
            return true;
        }

    private:
        bool m_Reported = false;
    };

    [[nodiscard]] constexpr bool RendererShellCanRunFrame(
        const RendererShellFailureLatch& failure) noexcept
    {
        return !failure.Failed();
    }

    enum class RendererRenderDisposition : uint8_t
    {
        Inactive,
        Pending,
        Failed
    };

    [[nodiscard]] constexpr RendererRenderDisposition
        MergeRendererRenderDisposition(
            RendererRenderDisposition current,
            RendererRenderDisposition reported) noexcept
    {
        return static_cast<uint8_t>(reported) >
                static_cast<uint8_t>(current)
            ? reported
            : current;
    }

    template <typename Publish>
    [[nodiscard]] bool ConsumeRendererRenderDisposition(
        RendererRenderDisposition disposition,
        Publish&& publish)
    {
        if (disposition == RendererRenderDisposition::Failed)
            return false;
        if (disposition == RendererRenderDisposition::Pending)
            return true;
        return static_cast<bool>(std::forward<Publish>(publish)());
    }

    template <typename Wait, typename LastError>
    [[nodiscard]] RendererShellOperationResult WaitForRendererShellFence(
        RendererShellOperation operation,
        Wait&& wait,
        LastError&& lastError)
    {
        const uint32_t waitResult = static_cast<uint32_t>(
            std::forward<Wait>(wait)(
                RendererShellWaitTimeoutMilliseconds));
        if (waitResult == RendererShellWaitSucceeded)
            return {};
        return {
            operation,
            waitResult,
            waitResult == RendererShellWaitFailed
                ? static_cast<uint32_t>(
                    std::forward<LastError>(lastError)())
                : 0u
        };
    }

    [[nodiscard]] constexpr RendererShellOperationResult
        CheckRendererShellBoolean(
            RendererShellOperation operation,
            bool succeeded) noexcept
    {
        return succeeded
            ? RendererShellOperationResult{}
            : RendererShellOperationResult{ operation, 0u, 0u };
    }

    template <typename Present, typename ResetFenceEvent,
        typename SetFenceEvent, typename SignalFence>
    [[nodiscard]] RendererShellOperationResult PresentRendererShellFrame(
        uint64_t fenceValue,
        Present&& present,
        ResetFenceEvent&& resetFenceEvent,
        SetFenceEvent&& setFenceEvent,
        SignalFence&& signalFence)
    {
        const int32_t presentResult = static_cast<int32_t>(
            std::forward<Present>(present)());
        if (presentResult < 0)
        {
            return {
                RendererShellOperation::Present,
                static_cast<uint32_t>(presentResult),
                0u
            };
        }

        const int32_t resetResult = static_cast<int32_t>(
            std::forward<ResetFenceEvent>(resetFenceEvent)());
        if (resetResult < 0)
        {
            return {
                RendererShellOperation::ResetFenceEvent,
                static_cast<uint32_t>(resetResult),
                0u
            };
        }

        const int32_t eventResult = static_cast<int32_t>(
            std::forward<SetFenceEvent>(setFenceEvent)(fenceValue));
        if (eventResult < 0)
        {
            return {
                RendererShellOperation::SetFenceEvent,
                static_cast<uint32_t>(eventResult),
                0u
            };
        }

        const int32_t signalResult = static_cast<int32_t>(
            std::forward<SignalFence>(signalFence)(fenceValue));
        if (signalResult < 0)
        {
            return {
                RendererShellOperation::SignalFence,
                static_cast<uint32_t>(signalResult),
                0u
            };
        }

        return {};
    }

    template <typename Counter>
    [[nodiscard]] constexpr bool AdvanceRendererShellCounter(
        Counter& counter,
        RendererShellOperationResult result) noexcept
    {
        if (!result.Succeeded())
            return false;
        ++counter;
        return true;
    }

    template <typename WaitForInternalIdle,
        typename WaitForShellFences,
        typename Release>
    [[nodiscard]] bool ReleaseRendererShellResources(
        WaitForInternalIdle&& waitForInternalIdle,
        WaitForShellFences&& waitForShellFences,
        Release&& release)
    {
        if (!std::forward<WaitForInternalIdle>(waitForInternalIdle)())
            return false;
        if (!std::forward<WaitForShellFences>(waitForShellFences)())
            return false;
        std::forward<Release>(release)();
        return true;
    }
}
